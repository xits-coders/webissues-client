/**************************************************************************
* This file is part of the WebIssues Desktop Client program
* Copyright (C) 2006 Michał Męciński
* Copyright (C) 2007-2012 WebIssues Team
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "issuedetailsgenerator.h"

#include "data/datamanager.h"
#include "data/entities.h"
#include "utils/datetimehelper.h"
#include "utils/viewsettingshelper.h"
#include "utils/textwriter.h"
#include "utils/formatter.h"

#include <QtAlgorithms>

IssueDetailsGenerator::IssueDetailsGenerator() :
    m_issueId( 0 ),
    m_history( NoHistory ),
    m_isAdmin( false ),
    m_commentsCount( 0 ),
    m_filesCount( 0 )
{
}

IssueDetailsGenerator::~IssueDetailsGenerator()
{
}

void IssueDetailsGenerator::setIssue( int issueId, History history )
{
    m_issueId = issueId;
    m_history = history;

    m_isAdmin = IssueEntity::isAdmin( issueId );
}

void IssueDetailsGenerator::write( TextWriter* writer, TextWithLinks::Flags flags /*= 0*/ )
{
    IssueEntity issue = IssueEntity::find( m_issueId );

    if ( issue.isValid() ) {
        writer->writeBlock( issue.name(), TextWriter::Header1Block );

        writer->createLayout( 1, 2 );

        QList<ValueEntity> values;
        if ( dataManager->setting( "hide_empty_values" ) == "1" )
            values = issue.nonEmptyValues();
        else
            values = issue.values();

        if ( values.isEmpty() )
            writer->mergeLayoutCells( 0, 0, 1, 2 );

        writer->gotoLayoutCell( 0, 0, TextWriter::NormalCell );
        writer->writeBlock( tr( "Properties" ), TextWriter::Header2Block );
        writeProperties( writer, issue );

        if ( !values.isEmpty() ) {
            writer->gotoLayoutCell( 0, 1, TextWriter::NormalCell );
            writer->writeBlock( tr( "Attributes" ), TextWriter::Header2Block );
            writeAttributes( writer, values, flags );
        }

        if ( m_history != NoHistory ) {
            IssueDescriptionEntity description = issue.description();

            if ( description.isValid() ) {
                writer->appendLayoutRows( 2 );

                writer->gotoLayoutCell( 1, 0, TextWriter::NormalCell );
                writer->writeBlock( tr( "Description" ), TextWriter::Header2Block );

                TextWithLinks result( flags );
                Formatter formatter;
                result.appendText( tr( "Last Edited:" ) );
                result.appendText( QString::fromUtf8( " %1 — %2" ).arg( formatter.formatDateTime( description.modifiedDate(), true ), description.modifiedUser() ) );

                writer->gotoLayoutCell( 1, 1, TextWriter::NormalCell );
                writer->writeBlock( result, TextWriter::LinksBlock );

                writer->mergeLayoutCells( 2, 0, 1, 2 );
                writer->gotoLayoutCell( 2, 0, TextWriter::CommentCell );
                writer->writeBlock( TextWithLinks::parse( description.text(), flags ), TextWriter::NormalBlock );
            }

            int row = writer->appendLayoutRows( 1 );

            writer->gotoLayoutCell( row, 0, TextWriter::NormalCell );
            writer->writeBlock( tr( "Issue History" ), TextWriter::Header2Block );

            if ( !flags.testFlag( TextWithLinks::NoInternalLinks ) ) {
                writer->gotoLayoutCell( row, 1, TextWriter::LinksCell );
                writer->writeBlock( historyLinks( flags ), TextWriter::LinksBlock );
            }

            writeHistory( writer, issue, flags );
        }

        writer->endLayout();
    }
}

void IssueDetailsGenerator::writeProperties( TextWriter* writer, const IssueEntity& issue )
{
    QStringList headers;
    QList<TextWithLinks> items;

    headers.append( tr( "ID:" ) );
    items.append( QString( "#%1" ).arg( issue.id() ) );

    FolderEntity folder = issue.folder();

    headers.append( tr( "Type:" ) );
    items.append( folder.type().name() );
    headers.append( tr( "Location:" ) );
    items.append( folder.project().name() + QString::fromUtf8( " — " ) + folder.name() );

    Formatter formatter;

    headers.append( tr( "Created:" ) );
    items.append( QString::fromUtf8( "%1 — %2" ).arg( formatter.formatDateTime( issue.createdDate(), true ), issue.createdUser() ) );
    headers.append( tr( "Last Modified:" ) );
    items.append( QString::fromUtf8( "%1 — %2" ).arg( formatter.formatDateTime( issue.modifiedDate(), true ), issue.modifiedUser() ) );

    writer->writeInfoList( headers, items );
}

void IssueDetailsGenerator::writeAttributes( TextWriter* writer, const QList<ValueEntity>& values, TextWithLinks::Flags flags )
{
    QStringList headers;
    QList<TextWithLinks> items;

    Formatter formatter;

    for ( int i = 0; i < values.count(); i++ ) {
        const ValueEntity& value = values.at( i );
        headers.append( value.name() + tr( ":" ) );
        QString formattedValue = formatter.convertAttributeValue( value.definition(), value.value(), true );
        items.append( TextWithLinks::parse( formattedValue, flags ) );
    }

    writer->writeInfoList( headers, items );
}

void IssueDetailsGenerator::writeHistory( TextWriter* writer, const IssueEntity& issue, TextWithLinks::Flags flags )
{
    Qt::SortOrder order = Qt::AscendingOrder;

    if ( dataManager->setting( "history_order" ) == "desc" )
        order = Qt::DescendingOrder;

    QList<ChangeEntity> changes;
    if ( m_history == AllHistory )
        changes = issue.changes( order );
    else if ( m_history == OnlyComments )
        changes = issue.comments( order );
    else if ( m_history == OnlyFiles )
        changes = issue.files( order );
    else if ( m_history == CommentsAndFiles )
        changes = issue.commentsAndFiles( order );

    QList<TextWithLinks> list;

    int lastUserId = 0;
    QDateTime lastDate;

    for ( int i = 0; i < changes.count(); i++ ) {
        const ChangeEntity& change = changes.at( i );

        if ( change.type() == ValueChanged && change.attributeId() == 0 )
            continue;

        if ( change.type() <= ValueChanged && list.count() > 0 ) {
            if ( change.createdUserId() == lastUserId && lastDate.secsTo( change.createdDate() ) < 180 ) {
                list.append( formatChange( change, flags ) );
                continue;
            }
        }

        if ( list.count() > 0 ) {
            writer->writeBulletList( list );
            list.clear();
        }

        int row;

        switch ( change.type() ) {
            case IssueCreated:
            case IssueRenamed:
            case ValueChanged:
                row = writer->appendLayoutRows( 1 );
                writer->mergeLayoutCells( row, 0, 1, 2 );
                writer->gotoLayoutCell( row, 0, TextWriter::NormalCell );
                lastUserId = change.createdUserId();
                lastDate = change.createdDate();
                writer->writeBlock( formatStamp( change ), TextWriter::Header3Block );
                list.append( formatChange( change, flags ) );
                break;

            case CommentAdded:
                row = writer->appendLayoutRows( 2 );
                writer->gotoLayoutCell( row, 0, TextWriter::NormalCell );
                writer->writeBlock( formatStamp( change ), TextWriter::Header3Block, QString( "id%1" ).arg( change.id() ) );
                writer->gotoLayoutCell( row, 1, TextWriter::LinksCell );
                writer->writeBlock( changeLinks( change, flags ), TextWriter::LinksBlock );
                writer->mergeLayoutCells( row + 1, 0, 1, 2 );
                writer->gotoLayoutCell( row + 1, 0, TextWriter::CommentCell );
                writer->writeBlock( TextWithLinks::parse( change.comment().text(), flags ), TextWriter::NormalBlock );
                m_commentsCount++;
                break;

            case FileAdded:
                row = writer->appendLayoutRows( 2 );
                writer->gotoLayoutCell( row, 0, TextWriter::NormalCell );
                writer->writeBlock( formatStamp( change ), TextWriter::Header3Block, QString( "id%1" ).arg( change.id() ) );
                writer->gotoLayoutCell( row, 1, TextWriter::LinksCell );
                writer->writeBlock( changeLinks( change, flags ), TextWriter::LinksBlock );
                writer->mergeLayoutCells( row + 1, 0, 1, 2 );
                writer->gotoLayoutCell( row + 1, 0, TextWriter::FileCell );
                writer->writeBlock( formatFile( change.file(), flags ), TextWriter::NormalBlock );
                m_filesCount++;
                break;

            case IssueMoved:
                row = writer->appendLayoutRows( 1 );
                writer->mergeLayoutCells( row, 0, 1, 2 );
                writer->gotoLayoutCell( row, 0, TextWriter::NormalCell );
                writer->writeBlock( formatStamp( change ), TextWriter::Header3Block );
                writer->writeBulletList( QList<TextWithLinks>() << formatChange( change, flags ) );
                break;
        }
    }

    if ( list.count() > 0 )
        writer->writeBulletList( list );

    if ( changes.count() == 0 ) {
        int row = writer->appendLayoutRows( 1 );
        writer->mergeLayoutCells( row, 0, 1, 2 );
        writer->gotoLayoutCell( row, 0, TextWriter::NormalCell );
        if ( m_history == OnlyComments )
            writer->writeBlock( tr( "There are no comments." ), TextWriter::NormalBlock );
        else if ( m_history == OnlyFiles )
            writer->writeBlock( tr( "There are no attachments." ), TextWriter::NormalBlock );
        else if ( m_history == CommentsAndFiles )
            writer->writeBlock( tr( "There are no comments or attachments." ), TextWriter::NormalBlock );
    }
}

QString IssueDetailsGenerator::formatStamp( const ChangeEntity& change )
{
    Formatter formatter;
    return QString::fromUtf8( "%1 — %2" ).arg( formatter.formatDateTime( change.createdDate(), true ), change.createdUser() );
}

TextWithLinks IssueDetailsGenerator::formatChange( const ChangeEntity& change, TextWithLinks::Flags flags )
{
    TextWithLinks result( flags );

    switch ( change.type() ) {
        case IssueCreated:
            result.appendText( tr( "Name:" ) );
            result.appendText( " \"" );
            result.appendParsed( change.newValue() );
            result.appendText( "\"" );
            break;

        case IssueRenamed:
            result.appendText( tr( "Name:" ) );
            result.appendText( " \"" );
            result.appendParsed( change.oldValue() );
            result.appendText( QString::fromUtf8( "\" → \"" ) );
            result.appendParsed( change.newValue() );
            result.appendText( "\"" );
            break;

        case ValueChanged: {
            result.appendText( change.name() );
            result.appendText( tr( ":" ) );
            result.appendText( " " );

            Formatter formatter;
            DefinitionInfo info = change.definition();
            QString oldValue = formatter.convertAttributeValue( info, change.oldValue(), false );
            QString newValue = formatter.convertAttributeValue( info, change.newValue(), false );

            if ( oldValue.isEmpty() ) {
                result.appendText( tr( "empty" ) );
            } else {
                result.appendText( "\"" );
                result.appendParsed( oldValue );
                result.appendText( "\"" );
            }

            result.appendText( QString::fromUtf8( " → " ) );

            if ( newValue.isEmpty() ) {
                result.appendText( tr( "empty" ) );
            } else {
                result.appendText( "\"" );
                result.appendParsed( newValue );
                result.appendText( "\"" );
            }
            break;
        }

        case IssueMoved:
            result.appendText( tr( "Issue moved from" ) );
            result.appendText( " " );

            if ( change.fromFolder().isEmpty() ) {
                result.appendText( tr( "Unknown Folder" ) );
            } else {
                result.appendText( "\"" );
                result.appendText( change.fromFolder() );
                result.appendText( "\"" );
            }

            result.appendText( " " );
            result.appendText( tr( "to" ) );
            result.appendText( " " );

            if ( change.toFolder().isEmpty() ) {
                result.appendText( tr( "Unknown Folder" ) );
            } else {
                result.appendText( "\"" );
                result.appendText( change.toFolder() );
                result.appendText( "\"" );
            }
            break;

        default:
            break;
    }

    return result;
}

TextWithLinks IssueDetailsGenerator::formatFile( const FileEntity& file, TextWithLinks::Flags flags )
{
    TextWithLinks result( flags );

    result.appendLink( file.name(), QString( "attachment://%1" ).arg( file.id() ) );
    result.appendText( " (" );

    Formatter formatter;
    result.appendText( formatter.formatSize( file.size() ) );

    result.appendText( ")" );

    if ( !file.description().isEmpty() ) {
        result.appendText( QString::fromUtf8( " — " ) );
        result.appendParsed( file.description() );
    }

    return result;
}

TextWithLinks IssueDetailsGenerator::historyLinks( TextWithLinks::Flags flags )
{
    TextWithLinks result( flags );

    for ( int i = AllHistory; i <= CommentsAndFiles; i++ ) {
        if ( i != AllHistory )
            result.appendText( " | " );

        QString text;
        switch ( i ) {
            case AllHistory:
                text = tr( "All History" );
                break;
            case OnlyComments:
                text = tr( "Only Comments" );
                break;
            case OnlyFiles:
                text = tr( "Only Attachments" );
                break;
            case CommentsAndFiles:
                text = tr( "Comments & Attachments" );
                break;
        }

        if ( m_history == i )
            result.appendText( text );
        else
            result.appendLink( text, QString( "command://filter/%1" ).arg( i ) );
    }

    return result;
}

TextWithLinks IssueDetailsGenerator::changeLinks( const ChangeEntity& change, TextWithLinks::Flags flags )
{
    TextWithLinks result( flags );

    if ( change.stampId() != change.id() ) {
        Formatter formatter;
        result.appendText( tr( "Last Edited:" ) );
        result.appendText( QString::fromUtf8( " %1 — %2" ).arg( formatter.formatDateTime( change.modifiedDate(), true ), change.modifiedUser() ) );
        result.appendText( " | " );
    }

    if ( change.type() == CommentAdded )
        result.appendText( tr( "Comment #%1" ).arg( change.id() ) );
    else
        result.appendText( tr( "Attachment #%1" ).arg( change.id() ) );

    if ( !flags.testFlag( TextWithLinks::NoInternalLinks ) && ( m_isAdmin || change.createdUserId() == dataManager->currentUserId() ) ) {
        result.appendText( " | " );
        if ( change.type() == CommentAdded )
            result.appendLink( tr( "Edit" ), QString( "command://edit-comment/%1" ).arg( change.id() ) );
        else
            result.appendLink( tr( "Edit" ), QString( "command://edit-file/%1" ).arg( change.id() ) );

        result.appendText( " | " );
        if ( change.type() == CommentAdded )
            result.appendLink( tr( "Delete" ), QString( "command://delete-comment/%1" ).arg( change.id() ) );
        else
            result.appendLink( tr( "Delete" ), QString( "command://delete-file/%1" ).arg( change.id() ) );
    }

    return result;
}
